#![cfg_attr(not(feature = "std"), no_std)]

use liquid::storage;
use liquid_lang as liquid;

#[liquid::interface(name = auto)]
mod hello_world {
    extern "liquid" {
        fn set(&mut self, name: String);
        fn get(&self) -> String;
    }
}

#[liquid::contract]
mod hello_world_caller {
    use super::*;
    use hello_world::HelloWorld;

    #[liquid(storage)]
    struct HelloWorldCaller {
        hello_world: storage::Value<HelloWorld>,
    }

    #[liquid(methods)]
    impl HelloWorldCaller {
        pub fn new(&mut self, addr: Address) {
            self.hello_world.initialize(HelloWorld::at(addr));
        }

        pub fn set(&mut self, name: String) {
            (*self.hello_world).set(name);
        }

        pub fn get(&self) -> String {
            (*self.hello_world).get().unwrap()
        }
    }
}
