// SPDX-License-Identifier: MIT
pragma solidity ^0.8.25;

import {Script, console} from "forge-std/Script.sol";

import {KinjoOnboarding} from "../src/KinjoOnboarding.sol";
import {
    Grant,
    IVerifiableFactory,
    IPermissionedRegistry,
    IPermissionedResolver,
    RegistryRoles,
    ResolverRoles
} from "../src/IENSv2.sol";

/// ENSv2 Sepolia addresses (contracts-v2 tag sepolia-deployment-2026-09-15).
abstract contract Sepolia {
    IVerifiableFactory constant FACTORY = IVerifiableFactory(0x9e726Eb570beb6BCEb495AB8cdA7df517d4e841C);
    IPermissionedRegistry constant ETH_REGISTRY = IPermissionedRegistry(0x657eA849311d3D5823348ddEd7C2AaAFb3EDE09E);
    address constant USER_REGISTRY_IMPL = 0xA80338aAA8D23831cEa25E858D1774534aBb0263;
    address constant RESOLVER_IMPL = 0x14F09Fd05d4585759e54844DC9B00147131Cf243;
}

/// One-time setup, run by the wallet that owns kinjo.eth. Shared by the deploy script and the tests.
abstract contract KinjoSetup is Sepolia {
    struct Deployment {
        IPermissionedRegistry kinjoRegistry;
        IPermissionedRegistry verifiedRegistry;
        IPermissionedResolver verifiedResolver;
        KinjoOnboarding onboarding;
    }

    /// Must run with `team` as the sender (broadcast or prank). `salt` must be new for each run by
    /// the same wallet: the factory refuses a salt it has seen from that sender.
    function _setUpKinjo(address team, uint256 salt) internal returns (Deployment memory d) {
        Grant[] memory teamOnly = new Grant[](1);
        teamOnly[0] = Grant(team, RegistryRoles.ALL);

        // 1. kinjo.eth gets its own subname registry.
        d.kinjoRegistry = _deployRegistry(teamOnly, salt);
        ETH_REGISTRY.setSubregistry(uint256(keccak256("kinjo")), address(d.kinjoRegistry));
        d.kinjoRegistry.setParent(address(ETH_REGISTRY), "kinjo");

        // 2. verified.kinjo.eth: team-owned registry and resolver for World ID badges.
        d.verifiedRegistry = _deployRegistry(teamOnly, salt + 1);
        d.verifiedResolver = IPermissionedResolver(
            FACTORY.deployProxy(RESOLVER_IMPL, salt + 2, abi.encodeCall(IPermissionedResolver.initialize, (teamOnly, new bytes[](0))))
        );
        d.kinjoRegistry.register(
            "verified", team, address(d.verifiedRegistry), address(d.verifiedResolver), 0, type(uint64).max
        );
        d.verifiedRegistry.setParent(address(d.kinjoRegistry), "verified");

        // 3. The onboarding contract and the roles it needs.
        d.onboarding = new KinjoOnboarding(
            team, FACTORY, d.kinjoRegistry, USER_REGISTRY_IMPL, RESOLVER_IMPL, d.verifiedRegistry, d.verifiedResolver
        );
        uint256 registrar = RegistryRoles.REGISTRAR | RegistryRoles.UNREGISTER;
        d.kinjoRegistry.grantRootRoles(registrar, address(d.onboarding));
        d.verifiedRegistry.grantRootRoles(registrar, address(d.onboarding));
        d.verifiedResolver.grantRootRoles(ResolverRoles.SET_TEXT | ResolverRoles.SET_ADDRESS, address(d.onboarding));
    }

    function _deployRegistry(Grant[] memory grants, uint256 salt) private returns (IPermissionedRegistry) {
        return IPermissionedRegistry(
            FACTORY.deployProxy(USER_REGISTRY_IMPL, salt, abi.encodeCall(IPermissionedRegistry.initialize, (grants)))
        );
    }
}

contract Deploy is Script, KinjoSetup {
    function run() external {
        // Set KINJO_SALT to a new value (e.g. 10, 20) for a second deploy by the same wallet.
        uint256 salt = vm.envOr("KINJO_SALT", uint256(1));
        vm.startBroadcast();
        (, address team,) = vm.readCallers(); // the wallet that signs (--account or --sender)
        Deployment memory d = _setUpKinjo(team, salt);
        vm.stopBroadcast();
        console.log("kinjo.eth registry:   ", address(d.kinjoRegistry));
        console.log("verified.kinjo.eth registry:", address(d.verifiedRegistry));
        console.log("verified.kinjo.eth resolver:", address(d.verifiedResolver));
        console.log("KinjoOnboarding:      ", address(d.onboarding));
    }
}
