// SPDX-License-Identifier: MIT
pragma solidity ^0.8.25;

import {Ownable} from "@openzeppelin/contracts/access/Ownable.sol";
import {Address} from "@openzeppelin/contracts/utils/Address.sol";
import {ECDSA} from "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";
import {EIP712} from "@openzeppelin/contracts/utils/cryptography/EIP712.sol";
import {SignatureChecker} from "@openzeppelin/contracts/utils/cryptography/SignatureChecker.sol";
import {Strings} from "@openzeppelin/contracts/utils/Strings.sol";

import {
    Grant,
    IVerifiableFactory,
    IPermissionedRegistry,
    IPermissionedResolver,
    RegistryRoles,
    ResolverRoles
} from "./IENSv2.sol";

/// @notice Puts people and their devices into ENS under kinjo.eth, one transaction per action.
/// See docs/onboarding.md. Every owner function works two ways: the owner calls it directly
/// (empty signature), or anyone submits it with the owner's EIP-712 signature (sponsored).
contract KinjoOnboarding is Ownable, EIP712 {
    struct Account {
        IPermissionedRegistry registry; // device subnames live here
        IPermissionedResolver resolver; // records of the owner name and all devices
        string label; // "alice" for alice.kinjo.eth
        bool verifiedHuman; // has alice.verified.kinjo.eth
    }

    bytes32 private constant JOIN_TYPEHASH = keccak256(
        "Join(address owner,string label,string deviceLabel,bytes32 deviceKey,uint256 nonce,uint256 deadline)"
    );
    bytes32 private constant ADD_DEVICE_TYPEHASH =
        keccak256("AddDevice(address owner,string deviceLabel,bytes32 deviceKey,uint256 nonce,uint256 deadline)");
    bytes32 private constant REVOKE_DEVICE_TYPEHASH =
        keccak256("RevokeDevice(address owner,string deviceLabel,uint256 nonce,uint256 deadline)");

    string public constant KEY_ENCRYPTION = "xyz.kinjo.encryption-key";
    string public constant KEY_KIND = "xyz.kinjo.kind";
    string public constant KEY_PROTOCOL = "xyz.kinjo.protocol";
    string public constant PROTOCOL = "kinjo/0.1";
    string public constant KEY_VERIFIED_HUMAN = "xyz.kinjo.verified-human";
    uint64 private constant NO_EXPIRY = type(uint64).max;
    uint256 private constant COIN_TYPE_ETH = 60;

    IVerifiableFactory public immutable factory;
    IPermissionedRegistry public immutable kinjoRegistry;
    address public immutable registryImpl;
    address public immutable resolverImpl;
    /// Registry and resolver of verified.kinjo.eth, owned by the team. Owners hold no roles there,
    /// so a badge name like alice.verified.kinjo.eth can't be forged.
    IPermissionedRegistry public immutable verifiedRegistry;
    IPermissionedResolver public immutable verifiedResolver;

    uint256 public fee;
    uint256 private _deployCount;
    mapping(address owner => Account) private _accounts;
    mapping(bytes32 labelHash => address owner) public ownerOfLabel;
    mapping(address owner => uint256) public nonces;

    event Joined(address indexed owner, string label, address registry, address resolver);
    event DeviceAdded(address indexed owner, string deviceLabel, bytes32 deviceKey);
    event DeviceRevoked(address indexed owner, string deviceLabel);
    event Released(address indexed owner, string label);
    event VerifiedHumanSet(address indexed owner, bool verified);
    event FeeSet(uint256 fee);

    error AlreadyJoined();
    error NotJoined();
    error LabelTaken();
    error InvalidLabel();
    error InvalidSignature();
    error Expired();
    error WrongFee();

    constructor(
        address initialOwner,
        IVerifiableFactory factory_,
        IPermissionedRegistry kinjoRegistry_,
        address registryImpl_,
        address resolverImpl_,
        IPermissionedRegistry verifiedRegistry_,
        IPermissionedResolver verifiedResolver_
    ) Ownable(initialOwner) EIP712("KinjoOnboarding", "1") {
        factory = factory_;
        kinjoRegistry = kinjoRegistry_;
        registryImpl = registryImpl_;
        resolverImpl = resolverImpl_;
        verifiedRegistry = verifiedRegistry_;
        verifiedResolver = verifiedResolver_;
    }

    // --- Owner actions ---

    /// @notice Claims `label.kinjo.eth` and registers the first device `deviceLabel.label.kinjo.eth`.
    function join(
        address owner,
        string calldata label,
        string calldata deviceLabel,
        bytes32 deviceKey,
        uint256 deadline,
        bytes calldata signature
    ) external payable {
        _authorize(owner, _joinHash(owner, label, deviceLabel, deviceKey, deadline), deadline, signature);
        _checkLabel(label);
        _checkLabel(deviceLabel);
        if (address(_accounts[owner].registry) != address(0)) revert AlreadyJoined();
        bytes32 labelHash = keccak256(bytes(label));
        if (ownerOfLabel[labelHash] != address(0)) revert LabelTaken();

        (IPermissionedRegistry registry, IPermissionedResolver resolver) = _deployAccount(owner);
        _accounts[owner] = Account(registry, resolver, label, false);
        ownerOfLabel[labelHash] = owner;

        // register() mints a token to the owner, which can call back into the owner's code.
        kinjoRegistry.register(label, owner, address(registry), address(resolver), _nameRoles(), NO_EXPIRY);
        registry.setParent(address(kinjoRegistry), label);
        resolver.setAddress(_dnsName(label), COIN_TYPE_ETH, abi.encodePacked(owner));
        emit Joined(owner, label, address(registry), address(resolver));

        _addDevice(owner, deviceLabel, deviceKey);
    }

    function addDevice(
        address owner,
        string calldata deviceLabel,
        bytes32 deviceKey,
        uint256 deadline,
        bytes calldata signature
    ) external payable {
        _authorize(
            owner,
            keccak256(
                abi.encode(ADD_DEVICE_TYPEHASH, owner, keccak256(bytes(deviceLabel)), deviceKey, nonces[owner], deadline)
            ),
            deadline,
            signature
        );
        _checkLabel(deviceLabel);
        _addDevice(owner, deviceLabel, deviceKey);
    }

    /// @notice Unregisters the device and clears its key. Clearing matters: after unregister, ENSv2
    /// falls back to the parent's resolver, which is the same resolver holding the old record.
    function revokeDevice(address owner, string calldata deviceLabel, uint256 deadline, bytes calldata signature)
        external
        payable
    {
        _authorize(
            owner,
            keccak256(abi.encode(REVOKE_DEVICE_TYPEHASH, owner, keccak256(bytes(deviceLabel)), nonces[owner], deadline)),
            deadline,
            signature
        );
        Account storage account = _joined(owner);
        account.registry.unregister(uint256(keccak256(bytes(deviceLabel))));
        account.resolver.setText(_dnsName(deviceLabel, account.label), KEY_ENCRYPTION, "");
        emit DeviceRevoked(owner, deviceLabel);
    }

    // --- Team actions ---

    /// @notice Frees `label.kinjo.eth` so it can be claimed again (demo reset).
    function release(string calldata label) external onlyOwner {
        bytes32 labelHash = keccak256(bytes(label));
        address owner = ownerOfLabel[labelHash];
        if (owner == address(0)) revert NotJoined();
        if (_accounts[owner].verifiedHuman) _removeBadge(label);
        delete _accounts[owner];
        delete ownerOfLabel[labelHash];
        kinjoRegistry.unregister(uint256(labelHash));
        emit Released(owner, label);
    }

    /// @notice Set by the relayer after it checked a World ID proof. Registers or removes
    /// `label.verified.kinjo.eth`, owned by the team, with `addr` = the owner's address and
    /// `xyz.kinjo.verified-human` = "world". Readers accept the badge only if that addr equals the
    /// addr of `label.kinjo.eth`, which ties it to the person and not just the label.
    function setVerifiedHuman(address owner, bool verified) external onlyOwner {
        Account storage account = _joined(owner);
        if (account.verifiedHuman == verified) return;
        account.verifiedHuman = verified;
        if (verified) {
            verifiedRegistry.register(account.label, msg.sender, address(0), address(verifiedResolver), 0, NO_EXPIRY);
            bytes memory name = _badgeName(account.label);
            verifiedResolver.setAddress(name, COIN_TYPE_ETH, abi.encodePacked(owner));
            verifiedResolver.setText(name, KEY_VERIFIED_HUMAN, "world");
        } else {
            _removeBadge(account.label);
        }
        emit VerifiedHumanSet(owner, verified);
    }

    function setFee(uint256 fee_) external onlyOwner {
        fee = fee_;
        emit FeeSet(fee_);
    }

    function withdraw(address payable to) external onlyOwner {
        Address.sendValue(to, address(this).balance);
    }

    // --- Views ---

    function accountOf(address owner)
        external
        view
        returns (address registry, address resolver, string memory label, bool verifiedHuman)
    {
        Account storage a = _accounts[owner];
        return (address(a.registry), address(a.resolver), a.label, a.verifiedHuman);
    }

    function domainSeparator() external view returns (bytes32) {
        return _domainSeparatorV4();
    }

    /// @notice DNS wire format of `labels[0].labels[1]...kinjo.eth`, as ENSv2 resolvers expect.
    function dnsName(string calldata deviceLabel, string calldata label) external pure returns (bytes memory) {
        return _dnsName(deviceLabel, label);
    }

    // --- Internal ---

    /// @dev The owner gets every role. This contract keeps only what later sponsored calls need.
    function _deployAccount(address owner) private returns (IPermissionedRegistry registry, IPermissionedResolver resolver) {
        Grant[] memory grants = new Grant[](2);
        grants[0] = Grant(owner, RegistryRoles.ALL);
        grants[1] = Grant(address(this), RegistryRoles.REGISTRAR | RegistryRoles.UNREGISTER | RegistryRoles.SET_PARENT);
        registry = IPermissionedRegistry(
            factory.deployProxy(registryImpl, ++_deployCount, abi.encodeCall(IPermissionedRegistry.initialize, (grants)))
        );

        grants[1] = Grant(address(this), ResolverRoles.SET_TEXT | ResolverRoles.SET_ADDRESS);
        resolver = IPermissionedResolver(
            factory.deployProxy(
                resolverImpl, ++_deployCount, abi.encodeCall(IPermissionedResolver.initialize, (grants, new bytes[](0)))
            )
        );
    }

    /// @dev Roles the owner gets on a name: point it elsewhere and transfer it. Unregister stays
    /// with the registry above (the team for owner names, the owner for devices).
    function _nameRoles() private pure returns (uint256) {
        uint256 roles = RegistryRoles.SET_SUBREGISTRY | RegistryRoles.SET_RESOLVER;
        return roles | RegistryRoles.admin(roles) | RegistryRoles.CAN_TRANSFER_ADMIN;
    }

    function _joinHash(
        address owner,
        string calldata label,
        string calldata deviceLabel,
        bytes32 deviceKey,
        uint256 deadline
    ) private view returns (bytes32) {
        return keccak256(
            abi.encode(
                JOIN_TYPEHASH,
                owner,
                keccak256(bytes(label)),
                keccak256(bytes(deviceLabel)),
                deviceKey,
                nonces[owner],
                deadline
            )
        );
    }

    function _addDevice(address owner, string calldata deviceLabel, bytes32 deviceKey) private {
        Account storage account = _joined(owner);
        account.registry.register(deviceLabel, owner, address(0), address(account.resolver), _nameRoles(), NO_EXPIRY);

        bytes memory name = _dnsName(deviceLabel, account.label);
        account.resolver.setText(name, KEY_ENCRYPTION, Strings.toHexString(uint256(deviceKey), 32));
        account.resolver.setText(name, KEY_KIND, "device");
        account.resolver.setText(name, KEY_PROTOCOL, PROTOCOL);
        emit DeviceAdded(owner, deviceLabel, deviceKey);
    }

    /// @dev Records are cleared too: after unregister, lookups fall back to verified.kinjo.eth's
    /// resolver, which is the same resolver that holds the badge records.
    function _removeBadge(string memory label) private {
        verifiedRegistry.unregister(uint256(keccak256(bytes(label))));
        bytes memory name = _badgeName(label);
        verifiedResolver.setAddress(name, COIN_TYPE_ETH, "");
        verifiedResolver.setText(name, KEY_VERIFIED_HUMAN, "");
    }

    function _badgeName(string memory label) private pure returns (bytes memory) {
        return _dnsName(label, "verified");
    }

    function _authorize(address owner, bytes32 structHash, uint256 deadline, bytes calldata signature) private {
        if (msg.value != fee) revert WrongFee();
        if (msg.sender != owner) {
            if (block.timestamp > deadline) revert Expired();
            // Key signature first: EIP-7702 accounts have code but still sign with their key.
            // Otherwise ask the account itself (ERC-1271 smart wallets).
            bytes32 digest = _hashTypedDataV4(structHash);
            (address signer, ECDSA.RecoverError err,) = ECDSA.tryRecover(digest, signature);
            bool keySigned = err == ECDSA.RecoverError.NoError && signer == owner;
            if (!keySigned && !SignatureChecker.isValidERC1271SignatureNow(owner, digest, signature)) {
                revert InvalidSignature();
            }
        }
        nonces[owner]++;
    }

    function _joined(address owner) private view returns (Account storage account) {
        account = _accounts[owner];
        if (address(account.registry) == address(0)) revert NotJoined();
    }

    /// @dev 1 to 32 characters of a-z, 0-9 and "-". Keeps names ENS-normalized without a library.
    function _checkLabel(string calldata label) private pure {
        bytes calldata b = bytes(label);
        if (b.length == 0 || b.length > 32) revert InvalidLabel();
        for (uint256 i; i < b.length; ++i) {
            bytes1 c = b[i];
            if (!((c >= "a" && c <= "z") || (c >= "0" && c <= "9") || c == "-")) revert InvalidLabel();
        }
    }

    /// @dev Labels are at most 32 bytes (_checkLabel), so the length fits in one byte.
    function _dnsName(string memory label) private pure returns (bytes memory) {
        // forge-lint: disable-next-line(unsafe-typecast)
        return abi.encodePacked(uint8(bytes(label).length), label, hex"056b696e6a6f", hex"03657468", hex"00");
    }

    /// @dev `child.label.kinjo.eth`, e.g. a device or a badge name.
    function _dnsName(string memory child, string memory label) private pure returns (bytes memory) {
        // forge-lint: disable-next-line(unsafe-typecast)
        return abi.encodePacked(uint8(bytes(child).length), child, _dnsName(label));
    }
}
