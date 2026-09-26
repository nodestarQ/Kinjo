// SPDX-License-Identifier: MIT
pragma solidity ^0.8.25;

import {Script, console} from "forge-std/Script.sol";

import {KinjoOnboarding} from "../src/KinjoOnboarding.sol";
import {Grant, IVerifiableFactory, IPermissionedRegistry, RegistryRoles} from "../src/IENSv2.sol";

/// One-time setup, run by the wallet that owns kinjo.eth:
///   1. deploy the kinjo.eth subname registry and point kinjo.eth at it
///   2. deploy KinjoOnboarding and let it register and unregister names under kinjo.eth
contract Deploy is Script {
    IVerifiableFactory constant FACTORY = IVerifiableFactory(0x9e726Eb570beb6BCEb495AB8cdA7df517d4e841C);
    IPermissionedRegistry constant ETH_REGISTRY = IPermissionedRegistry(0x657eA849311d3D5823348ddEd7C2AaAFb3EDE09E);
    address constant USER_REGISTRY_IMPL = 0xA80338aAA8D23831cEa25E858D1774534aBb0263;
    address constant RESOLVER_IMPL = 0x14F09Fd05d4585759e54844DC9B00147131Cf243;

    function run() external {
        vm.startBroadcast();
        address team = msg.sender;

        Grant[] memory grants = new Grant[](1);
        grants[0] = Grant(team, RegistryRoles.ALL);
        IPermissionedRegistry kinjoRegistry = IPermissionedRegistry(
            FACTORY.deployProxy(USER_REGISTRY_IMPL, 1, abi.encodeCall(IPermissionedRegistry.initialize, (grants)))
        );
        ETH_REGISTRY.setSubregistry(uint256(keccak256("kinjo")), address(kinjoRegistry));
        kinjoRegistry.setParent(address(ETH_REGISTRY), "kinjo");

        KinjoOnboarding onboarding =
            new KinjoOnboarding(team, FACTORY, kinjoRegistry, USER_REGISTRY_IMPL, RESOLVER_IMPL);
        kinjoRegistry.grantRootRoles(RegistryRoles.REGISTRAR | RegistryRoles.UNREGISTER, address(onboarding));

        vm.stopBroadcast();
        console.log("kinjo.eth registry:", address(kinjoRegistry));
        console.log("KinjoOnboarding:   ", address(onboarding));
    }
}
