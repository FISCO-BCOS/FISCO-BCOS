#![cfg_attr(not(feature = "std"), no_std)]

use liquid::storage;
use liquid_lang as liquid;
use liquid_lang::InOut;
use core::clone::Clone;
use liquid_prelude::string::String;
use liquid_primitives::types::timestamp;
use liquid_primitives::types::Address;

#[derive(InOut, Clone)]
pub struct TraceData {
    pub addr: Address,
    pub status: i16,
    pub time: timestamp,
    pub remark: String,
}

#[liquid::contract]
mod goods {
    use super::*;

    #[liquid(storage)]
    struct Goods {
        pub goodsId: storage::Value<u64>,
        pub goodsName: storage::Value<String>,
        pub status: storage::Value<i16>,
        pub traceData: storage::Vec<TraceData>,
    }

    #[liquid(methods)]
    impl Goods {
        pub fn new(&mut self, goodsId: u64, goodsName: String) {
            self.goodsId.initialize(goodsId);
            self.goodsName.initialize(goodsName);
            self.traceData.initialize();
            self.status.initialize(0);
            let addr: Address = self.env().get_tx_origin();
            let status: i16 = 0;
            let time: timestamp = self.env().now();
            let remark: String = String::from("create");
            self.traceData.push(TraceData{addr, status, time, remark,});
        }

        pub fn changeStatus(&mut self, goodsStatus: i16, remark: String) {
            self.status.set(goodsStatus);
            let addr: Address = self.env().get_tx_origin();
            let status: i16 = goodsStatus;
            let time: timestamp = self.env().now();
            let remark: String = remark;
            self.traceData.push(TraceData{addr, status, time, remark,});
        }

        pub fn getTraceInfo(&self) -> Vec<TraceData> {
            let mut traceData: Vec<TraceData> = Vec::<TraceData>::new();
            for elem in self.traceData.iter() {
                traceData.push(elem.clone());
            }
            return traceData;
        }

        pub fn getTraceInfoCount(&self) -> u32 {
            return self.traceData.len();
        }

        pub fn getTraceInfoByIndex(&self, index: u32) -> (Address, i16, timestamp, String) {
            let td = &self.traceData[index];
            return (td.addr.clone(), td.status, td.time, td.remark.clone());
        }

    }
}