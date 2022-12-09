#![cfg_attr(not(feature = "std"), no_std)]

use liquid::storage;
use liquid_lang as liquid;

#[liquid::contract]
mod transfer {
    use super::*;

    #[liquid(storage)]
    struct Transfer {
        accounts: storage::Mapping<String, u32>,
    }

    #[liquid(methods)]
    impl Transfer {
        pub fn new(&mut self) {
            self.accounts.initialize();
            self.accounts.insert(String::from("alice"), u32::MAX);
            self.accounts.insert(String::from("bob"), 0);
            self.accounts.insert(String::from("charlie"), u32::MAX);
            self.accounts.insert(String::from("david"), 0);
        }

        pub fn transfer(&mut self, from: String ,to: String, amount: u32) -> bool {
            if self.accounts[&from] < amount {
                false
            } else {
                self.accounts[&from] -= amount;
                self.accounts[&to] += amount;
                true
            }
        }

        pub fn query(&self, name: String) -> u32 {
            self.accounts[&name]
        }
    }
}