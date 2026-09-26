// SPDX-License-Identifier: MIT
pragma solidity ^0.8.25;

import {Test} from "forge-std/Test.sol";

import {KinjoOnboarding} from "../src/KinjoOnboarding.sol";
import {IPermissionedRegistry, IUniversalResolver, RegistryRoles} from "../src/IENSv2.sol";
import {KinjoSetup} from "../script/Deploy.s.sol";

interface ITextResolver {
    function text(bytes32 node, string calldata key) external view returns (string memory);
    function addr(bytes32 node) external view returns (address);
}

/// Runs against the real ENSv2 contracts on a Sepolia fork, acting as the team wallet that owns kinjo.eth.
contract KinjoOnboardingTest is Test, KinjoSetup {
    address constant TEAM = 0x281770ab3731C474a7F7ab00FfE0A4A92Bf6aCaD;
    IUniversalResolver constant UR = IUniversalResolver(0xeEeEEEeE14D718C2B47D9923Deab1335E144EeEe);

    bytes32 constant JOIN_TYPEHASH = keccak256(
        "Join(address owner,string label,string deviceLabel,bytes32 deviceKey,uint256 nonce,uint256 deadline)"
    );
    bytes32 constant ADD_DEVICE_TYPEHASH =
        keccak256("AddDevice(address owner,string deviceLabel,bytes32 deviceKey,uint256 nonce,uint256 deadline)");
    bytes32 constant REVOKE_DEVICE_TYPEHASH =
        keccak256("RevokeDevice(address owner,string deviceLabel,uint256 nonce,uint256 deadline)");

    bytes32 constant KEY_1 = 0x8d4a2f0c11e5b3c0f1d2e3a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8;
    bytes32 constant KEY_2 = 0x1111111111111111111111111111111111111111111111111111111111111111;

    KinjoOnboarding onboarding;
    Deployment d;
    // Not 0xA11CE: that well-known test key has an EIP-7702 delegation on Sepolia.
    uint256 alicePk = uint256(keccak256("kinjo test alice"));
    address alice;
    address relayer = makeAddr("relayer");

    function setUp() public {
        // Public RPCs only keep recent state, so fork the latest block unless FORK_BLOCK is set
        // (needs an archive RPC in SEPOLIA_RPC_URL).
        string memory rpc = vm.envOr("SEPOLIA_RPC_URL", string("https://ethereum-sepolia-rpc.publicnode.com"));
        uint256 forkBlock = vm.envOr("FORK_BLOCK", uint256(0));
        if (forkBlock == 0) vm.createSelectFork(rpc);
        else vm.createSelectFork(rpc, forkBlock);
        alice = vm.addr(alicePk);

        vm.startPrank(TEAM);
        d = _setUpKinjo(TEAM, 1);
        vm.stopPrank();
        onboarding = d.onboarding;
    }

    // --- helpers ---

    function _sign(bytes32 structHash) internal view returns (bytes memory) {
        bytes32 digest = keccak256(abi.encodePacked("\x19\x01", onboarding.domainSeparator(), structHash));
        (uint8 v, bytes32 r, bytes32 s) = vm.sign(alicePk, digest);
        return abi.encodePacked(r, s, v);
    }

    function _joinSig(string memory label, string memory device, bytes32 key, uint256 deadline)
        internal
        view
        returns (bytes memory)
    {
        return _sign(
            keccak256(
                abi.encode(
                    JOIN_TYPEHASH, alice, keccak256(bytes(label)), keccak256(bytes(device)), key, onboarding.nonces(alice), deadline
                )
            )
        );
    }

    function _join() internal {
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _joinSig("alice", "handheld", KEY_1, deadline);
        vm.prank(relayer);
        onboarding.join(alice, "alice", "handheld", KEY_1, deadline, sig);
    }

    function _dns(string memory name) internal pure returns (bytes memory out) {
        bytes memory b = bytes(name);
        uint256 start;
        for (uint256 i; i <= b.length; ++i) {
            if (i == b.length || b[i] == ".") {
                out = abi.encodePacked(out, uint8(i - start));
                for (uint256 j = start; j < i; ++j) {
                    out = abi.encodePacked(out, b[j]);
                }
                start = i + 1;
            }
        }
        out = abi.encodePacked(out, uint8(0));
    }

    function _namehash(string memory name) internal pure returns (bytes32 node) {
        bytes memory b = bytes(name);
        uint256 end = b.length;
        for (uint256 i = b.length; i > 0; --i) {
            if (b[i - 1] == ".") {
                node = keccak256(abi.encodePacked(node, keccak256(_slice(b, i, end))));
                end = i - 1;
            }
        }
        node = keccak256(abi.encodePacked(node, keccak256(_slice(b, 0, end))));
    }

    function _slice(bytes memory b, uint256 from, uint256 to) internal pure returns (bytes memory out) {
        out = new bytes(to - from);
        for (uint256 i; i < out.length; ++i) {
            out[i] = b[from + i];
        }
    }

    /// Resolves a text record through the Universal Resolver, like viem/ensjs would.
    function _text(string memory name, string memory key) internal view returns (string memory) {
        (bytes memory result,) = UR.resolve(_dns(name), abi.encodeCall(ITextResolver.text, (_namehash(name), key)));
        return abi.decode(result, (string));
    }

    function _addr(string memory name) internal view returns (address) {
        (bytes memory result,) = UR.resolve(_dns(name), abi.encodeCall(ITextResolver.addr, (_namehash(name))));
        return abi.decode(result, (address));
    }

    // --- tests ---

    function test_joinSponsored_resolvesThroughENS() public {
        _join();
        assertEq(
            _text("handheld.alice.kinjo.eth", "xyz.kinjo.encryption-key"),
            "0x8d4a2f0c11e5b3c0f1d2e3a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8"
        );
        assertEq(_text("handheld.alice.kinjo.eth", "xyz.kinjo.kind"), "device");
        assertEq(_text("handheld.alice.kinjo.eth", "xyz.kinjo.protocol"), "kinjo/0.1");
        assertEq(_addr("alice.kinjo.eth"), alice);
        assertEq(onboarding.ownerOfLabel(keccak256("alice")), alice);
        assertEq(onboarding.nonces(alice), 1);
    }

    function test_join_ownerHoldsAllRolesAndContractOnlyItsOwn() public {
        _join();
        (address registry,,,) = onboarding.accountOf(alice);
        assertTrue(IPermissionedRegistry(registry).hasRootRoles(RegistryRoles.ALL, alice));
        assertTrue(
            IPermissionedRegistry(registry).hasRootRoles(
                RegistryRoles.REGISTRAR | RegistryRoles.UNREGISTER, address(onboarding)
            )
        );
        assertFalse(IPermissionedRegistry(registry).hasRootRoles(RegistryRoles.admin(RegistryRoles.REGISTRAR), address(onboarding)));
    }

    function test_joinSelfPaid_noSignature() public {
        vm.prank(alice);
        onboarding.join(alice, "alice", "handheld", KEY_1, 0, "");
        assertEq(_text("handheld.alice.kinjo.eth", "xyz.kinjo.kind"), "device");
    }

    function test_join_rejectsBadSignature() public {
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _joinSig("alice", "handheld", KEY_1, deadline);
        vm.prank(relayer);
        vm.expectRevert(KinjoOnboarding.InvalidSignature.selector);
        onboarding.join(alice, "alice", "handheld", KEY_2, deadline, sig); // key swapped by the relayer
    }

    function test_join_rejectsReplayAndExpired() public {
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _joinSig("alice", "handheld", KEY_1, deadline);
        vm.prank(relayer);
        onboarding.join(alice, "alice", "handheld", KEY_1, deadline, sig);
        vm.expectRevert(KinjoOnboarding.InvalidSignature.selector); // nonce moved on
        onboarding.join(alice, "alice", "handheld", KEY_1, deadline, sig);

        address bobSigner = vm.addr(uint256(keccak256("kinjo test bob")));
        vm.warp(deadline + 1);
        vm.expectRevert(KinjoOnboarding.Expired.selector);
        onboarding.join(bobSigner, "bob", "node1", KEY_2, deadline, sig);
    }

    function test_join_rejectsTakenAndInvalidLabels() public {
        _join();
        address bob = vm.addr(uint256(keccak256("kinjo test bob"))); // makeAddr("bob") has code on Sepolia
        vm.startPrank(bob);
        vm.expectRevert(KinjoOnboarding.LabelTaken.selector);
        onboarding.join(bob, "alice", "node1", KEY_2, 0, "");
        vm.expectRevert(KinjoOnboarding.InvalidLabel.selector);
        onboarding.join(bob, "Bob", "node1", KEY_2, 0, "");
        vm.expectRevert(KinjoOnboarding.InvalidLabel.selector);
        onboarding.join(bob, "bob", "node.1", KEY_2, 0, "");
        vm.stopPrank();

        vm.prank(alice);
        vm.expectRevert(KinjoOnboarding.AlreadyJoined.selector);
        onboarding.join(alice, "alice2", "handheld", KEY_2, 0, "");
    }

    function test_addDeviceSponsored() public {
        _join();
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _sign(
            keccak256(
                abi.encode(ADD_DEVICE_TYPEHASH, alice, keccak256("laptop"), KEY_2, onboarding.nonces(alice), deadline)
            )
        );
        vm.prank(relayer);
        onboarding.addDevice(alice, "laptop", KEY_2, deadline, sig);
        assertEq(
            _text("laptop.alice.kinjo.eth", "xyz.kinjo.encryption-key"),
            "0x1111111111111111111111111111111111111111111111111111111111111111"
        );
    }

    function test_revokeDevice_clearsKeyAndCanReAdd() public {
        _join();
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _sign(
            keccak256(abi.encode(REVOKE_DEVICE_TYPEHASH, alice, keccak256("handheld"), onboarding.nonces(alice), deadline))
        );
        vm.prank(relayer);
        onboarding.revokeDevice(alice, "handheld", deadline, sig);

        (address registry,,,) = onboarding.accountOf(alice);
        assertEq(IPermissionedRegistry(registry).getResolver("handheld"), address(0));
        assertEq(_text("handheld.alice.kinjo.eth", "xyz.kinjo.encryption-key"), "");

        // Demo reset: the wiped handheld comes back with a new key.
        vm.prank(alice);
        onboarding.addDevice(alice, "handheld", KEY_2, 0, "");
        assertEq(
            _text("handheld.alice.kinjo.eth", "xyz.kinjo.encryption-key"),
            "0x1111111111111111111111111111111111111111111111111111111111111111"
        );
    }

    function test_release_letsTheNameBeClaimedAgain() public {
        _join();
        vm.expectRevert();
        onboarding.release("alice"); // team only

        vm.prank(TEAM);
        onboarding.release("alice");
        assertEq(onboarding.ownerOfLabel(keccak256("alice")), address(0));

        vm.prank(alice);
        onboarding.join(alice, "alice", "handheld", KEY_2, 0, "");
        assertEq(
            _text("handheld.alice.kinjo.eth", "xyz.kinjo.encryption-key"),
            "0x1111111111111111111111111111111111111111111111111111111111111111"
        );
    }

    function test_ownerKeepsControlWithoutTheContract() public {
        _join();
        (address registry,,,) = onboarding.accountOf(alice);
        vm.prank(alice);
        IPermissionedRegistry(registry).unregister(uint256(keccak256("handheld")));
        assertEq(IPermissionedRegistry(registry).getResolver("handheld"), address(0));
    }

    function test_eip7702Account_keySignatureStillWorks() public {
        _join();
        // Give alice's address delegation code without ERC-1271, like a 7702 smart account.
        vm.etch(alice, hex"ef0100db53cfe1f5d6bf0d9ffcbda8f240b1fdfee1dba0");
        uint256 deadline = block.timestamp + 1 hours;
        bytes memory sig = _sign(
            keccak256(abi.encode(REVOKE_DEVICE_TYPEHASH, alice, keccak256("handheld"), onboarding.nonces(alice), deadline))
        );
        vm.prank(relayer);
        onboarding.revokeDevice(alice, "handheld", deadline, sig);
        assertEq(_text("handheld.alice.kinjo.eth", "xyz.kinjo.encryption-key"), "");
    }

    function test_fee() public {
        vm.prank(TEAM);
        onboarding.setFee(0.001 ether);
        vm.deal(alice, 1 ether);
        vm.startPrank(alice);
        vm.expectRevert(KinjoOnboarding.WrongFee.selector);
        onboarding.join(alice, "alice", "handheld", KEY_1, 0, "");
        onboarding.join{value: 0.001 ether}(alice, "alice", "handheld", KEY_1, 0, "");
        vm.stopPrank();
        assertEq(address(onboarding).balance, 0.001 ether);
    }

    // --- World ID badge: alice.verified.kinjo.eth, owned by the team ---

    function _verify() internal {
        _join();
        vm.prank(TEAM);
        onboarding.setVerifiedHuman(alice, true);
    }

    function test_badge_resolvesAndMatchesTheOwnerName() public {
        _verify();
        assertEq(_text("alice.verified.kinjo.eth", "xyz.kinjo.verified-human"), "world");
        assertEq(_addr("alice.verified.kinjo.eth"), alice);
        assertEq(_addr("alice.verified.kinjo.eth"), _addr("alice.kinjo.eth")); // the reader's check
        (,,, bool verified) = onboarding.accountOf(alice);
        assertTrue(verified);
    }

    function test_badge_teamOnly() public {
        _join();
        vm.prank(alice);
        vm.expectRevert();
        onboarding.setVerifiedHuman(alice, true);
    }

    function test_badge_ownerCannotForgeIt() public {
        _join();
        bytes memory badge = onboarding.dnsName("alice", "verified");
        vm.startPrank(alice);
        vm.expectRevert();
        d.verifiedResolver.setText(badge, "xyz.kinjo.verified-human", "world");
        vm.expectRevert();
        d.verifiedRegistry.register("alice", alice, address(0), address(d.verifiedResolver), 0, type(uint64).max);
        vm.stopPrank();
        assertEq(_text("alice.verified.kinjo.eth", "xyz.kinjo.verified-human"), "");
    }

    function test_badge_removeClearsRecords() public {
        _verify();
        vm.prank(TEAM);
        onboarding.setVerifiedHuman(alice, false);
        assertEq(d.verifiedRegistry.getResolver("alice"), address(0));
        assertEq(_text("alice.verified.kinjo.eth", "xyz.kinjo.verified-human"), "");
        assertEq(_addr("alice.verified.kinjo.eth"), address(0));
    }

    function test_badge_goesAwayOnRelease_andDoesNotCarryToTheNextClaimer() public {
        _verify();
        vm.prank(TEAM);
        onboarding.release("alice");
        assertEq(_text("alice.verified.kinjo.eth", "xyz.kinjo.verified-human"), "");

        address bob = vm.addr(uint256(keccak256("kinjo test bob"))); // makeAddr("bob") has code on Sepolia
        vm.prank(bob);
        onboarding.join(bob, "alice", "handheld", KEY_2, 0, "");
        assertEq(_addr("alice.kinjo.eth"), bob);
        assertTrue(_addr("alice.verified.kinjo.eth") != bob);
    }

    function test_join_cannotClaimTheBadgeNamespace() public {
        vm.prank(alice);
        vm.expectRevert();
        onboarding.join(alice, "verified", "handheld", KEY_1, 0, "");
    }
}
