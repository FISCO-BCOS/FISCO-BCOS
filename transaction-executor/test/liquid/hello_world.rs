#![cfg_attr(not(feature = "std"), no_std)]

use liquid::{storage};
use liquid_lang as liquid;

#[liquid::contract]
mod hello_world {
    use super::*;

    #[liquid(storage)]
    struct HelloWorld {
        name: storage::Value<String>,
    }

    #[liquid(methods)]
    impl HelloWorld {
        pub fn new(&mut self, name: String) {
            self.name.initialize(name);
        }

        pub fn set(&mut self, name: String) {
            *self.name = name;
        }

        pub fn get(&self) -> String {
            self.name.clone()
        }
    }
}
