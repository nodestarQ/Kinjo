// SPDX-License-Identifier: MIT
pragma solidity ^0.8.25;

// The parts of ENSv2 (ensdomains/contracts-v2, tag sepolia-deployment-2026-09-15) that Kinjo uses.

struct Grant {
    address account;
    uint256 roleBitmap;
}

interface IVerifiableFactory {
    function deployProxy(address implementation, uint256 salt, bytes memory data) external returns (address proxy);
}

interface IPermissionedRegistry {
    function initialize(Grant[] calldata grants) external;
    function register(
        string calldata label,
        address owner,
        address subregistry,
        address resolver,
        uint256 roleBitmap,
        uint64 expiry
    ) external returns (uint256 tokenId);
    function unregister(uint256 anyId) external;
    function setSubregistry(uint256 anyId, address registry) external;
    function setParent(address parent, string calldata label) external;
    function grantRootRoles(uint256 roleBitmap, address account) external returns (bool);
    function hasRootRoles(uint256 roleBitmap, address account) external view returns (bool);
    function getSubregistry(string calldata label) external view returns (address);
    function getResolver(string calldata label) external view returns (address);
}

interface IPermissionedResolver {
    function initialize(Grant[] calldata grants, bytes[] calldata calls) external;
    function setText(bytes calldata name, string calldata key, string calldata value) external;
    function setAddress(bytes calldata name, uint256 coinType, bytes calldata addressBytes) external;
}

interface IUniversalResolver {
    function resolve(bytes calldata name, bytes calldata data) external view returns (bytes memory, address);
}

library RegistryRoles {
    uint256 internal constant REGISTRAR = 1 << 0;
    uint256 internal constant SET_PARENT = 1 << 8;
    uint256 internal constant UNREGISTER = 1 << 12;
    uint256 internal constant RENEW = 1 << 16;
    uint256 internal constant SET_SUBREGISTRY = 1 << 20;
    uint256 internal constant SET_RESOLVER = 1 << 24;
    uint256 internal constant CAN_TRANSFER_ADMIN = (1 << 28) << 128;
    uint256 internal constant ALL = 0x1111111111111111111111111111111111111111111111111111111111111111;

    function admin(uint256 roles) internal pure returns (uint256) {
        return roles << 128;
    }
}

library ResolverRoles {
    uint256 internal constant SET_ADDRESS = 1 << 0;
    uint256 internal constant SET_TEXT = 1 << 4;
}
