#![cfg_attr(not(feature = "std"), no_std)]

use liquid::storage;
use liquid_lang as liquid;
use liquid_lang::InOut;
use core::clone::Clone;
use liquid_prelude::string::String;
use liquid_prelude::vec::Vec;
use liquid_primitives::types::timestamp;
use liquid_primitives::types::Address;

#[derive(InOut, Clone)]
pub struct TraceInfo {
    addr: Address,
    status: i16,
    time: timestamp,
    remark: String,
}

#[derive(InOut, Clone)]
pub struct Goods {
    goodsId: u64,
    goodsName: String,
    status: i16,
    traceInfoList: Vec<TraceInfo>,
}

#[liquid::contract]
mod traceability_factory {
    use super::*;

    #[liquid(storage)]
    struct TraceabilityFactory {
        pub goodsMap: storage::Mapping<u64, Goods>,
    }

    #[liquid(methods)]
    impl TraceabilityFactory {
        pub fn new(&mut self) {
            self.goodsMap.initialize();
        }

        fn makeTraceInfo(&self, status: i16, remark: String) -> TraceInfo {
            let addr = self.env().get_tx_origin();
            let time = self.env().now();
            return TraceInfo { addr, status, time, remark };
        }

        pub fn addGoods(&mut self, goodsId: u64, goodsName: String) {
            if self.goodsMap.contains_key(&goodsId) {
                assert!(false, "The goods already exists!");
            }

            let status: i16 = 0;
            let traceInfo = self.makeTraceInfo(status, String::from("create"));
            let mut traceInfoList: Vec<TraceInfo> = Vec::new();
            traceInfoList.push(traceInfo);
            self.goodsMap.insert(goodsId, Goods { goodsId, goodsName, status, traceInfoList });
        }

        pub fn checkExists(&self, goodsId: u64) {
            if self.goodsMap.contains_key(&goodsId) == false {
                assert!(false, "The goods isn't exists!");
            }
        }

        pub fn changeStatus(&mut self, goodsId: u64, status: i16, remark: String) {
            self.checkExists(goodsId);

            let traceInfo = self.makeTraceInfo(status, remark);
            let mut goods = self.goodsMap.get_mut(&goodsId).unwrap();
            goods.status = status;
            goods.traceInfoList.push(traceInfo);
        }

        pub fn getTraceInfo(&self, goodsId: u64) -> Vec<TraceInfo> {
            self.checkExists(goodsId);

            let goods = &self.goodsMap.get(&goodsId).unwrap();
            let mut traceInfoList: Vec<TraceInfo> = Vec::new();
            for elem in goods.traceInfoList.iter() {
                traceInfoList.push(elem.clone());
            }
            return traceInfoList;
        }

        pub fn getTraceInfoCount(&self, goodsId: u64) -> u32 {
            self.checkExists(goodsId);

            let goods = &self.goodsMap.get(&goodsId).unwrap();
            return goods.traceInfoList.len() as u32;
        }

        pub fn getTraceInfoByIndex(&self, goodsId: u64, index: u32) -> (Address, i16, timestamp, String) {
            self.checkExists(goodsId);
            let goods = &self.goodsMap.get(&goodsId).unwrap();
            assert!(index < goods.traceInfoList.len() as u32, "Index out of bounds!");

            let td = &goods.traceInfoList.get(index as usize).unwrap();
            return (td.addr.clone(), td.status, td.time, td.remark.clone());
        }

    }
}