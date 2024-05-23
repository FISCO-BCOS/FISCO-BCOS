use core::panic;
use std::collections::HashMap;
use std::iter::zip;
use std::sync::Arc;

use env_logger::Env;
use log::info;
// use serde::Serialize;

use log::trace;
use rocksdb::{DBCompressionType, Options, DB};
use structopt::StructOpt;
use tide::log::debug;
use tide::prelude::*;
use tide::Body;
use tide::Request;
use tide::Response;
use tikv_client::TransactionOptions;
use tokio::sync::Mutex;

#[derive(StructOpt)]
struct Cli {
    /// The path of the abi json file
    #[structopt(parse(from_os_str))]
    #[structopt(short, long)]
    rocksdb_path: Option<std::path::PathBuf>,
    /// pd address of TiKV cluster
    #[structopt(
        short,
        long,
        conflicts_with = "rocksdb-path",
        required_unless = "rocksdb-path",
        // default_value = "127.0.0.1:2379"
    )]
    pd_addrs: Option<String>,
    /// The IP and port to listen
    #[structopt(short, long, default_value = "127.0.0.1:8080")]
    ip_port: String,
}

enum Storage {
    RocksDB(rocksdb::DB),
    TiKV(tikv_client::TransactionClient),
}

#[derive(Debug, Deserialize, Serialize)]
struct JsonRequest {
    jsonrpc: String,
    method: String,
    id: u64,
    params: serde_json::Value,
}

#[derive(Debug, Deserialize, Serialize)]
struct JsonError {
    message: String,
    code: i32,
    data: String,
}

#[derive(Debug, Deserialize, Serialize)]
struct JsonResponse {
    jsonrpc: String,
    id: u64,
    result: serde_json::Value,
    error: JsonError,
}
macro_rules! new_json_response {
    ($id:expr, $error:expr, $result:expr) => {
        JsonResponse {
            jsonrpc: "2.0".to_string(),
            id: $id,
            result: $result,
            error: $error,
        }
    };
}

#[tokio::main(flavor = "multi_thread", worker_threads = 4)]
async fn main() -> tide::Result<()> {
    let args = Cli::from_args();
    let storage;
    env_logger::Builder::from_env(Env::default().default_filter_or("info")).init();
    // TODO: if use tikv use env log of slog
    if let Some(rocksdb_path) = args.rocksdb_path {
        if rocksdb_path.exists() {
            info!("rocksdb path: {:?}", rocksdb_path);
            let mut db_opts = Options::default();
            db_opts.create_if_missing(true);
            db_opts.set_compression_type(DBCompressionType::Zstd);
            storage = Arc::new(Mutex::new(Storage::RocksDB(
                DB::open(&db_opts, rocksdb_path).unwrap(),
            )));
        } else {
            panic!("rocksdb path not exists");
        }
    } else if let Some(pd_addrs) = args.pd_addrs {
        info!("pd_addrs: {:?}", pd_addrs);
        let pd_endpoints = pd_addrs.split(",").map(|s| s.to_owned()).collect();
        let client = tikv_client::TransactionClient::new(pd_endpoints).await?;
        storage = Arc::new(Mutex::new(Storage::TiKV(client)));
    } else {
        panic!("one of rocksdb path/pd_addrs must be set");
    }
    info!("listen: {:?}", args.ip_port);
    let mut app = tide::with_state(storage);
    // let database = Arc::new(Mutex::new(storage));
    app.at("/")
        .post(|mut req: Request<Arc<Mutex<Storage>>>| async move {
            let request_json = req.body_json::<JsonRequest>().await?;
            trace!("request_json.params: {:?}", request_json);
            let method = request_json.method;
            if request_json.params.is_null() {
                let response = new_json_response!(
                    request_json.id,
                    JsonError {
                        message: "parameters is null".to_string(),
                        code: -1002,
                        data: "".to_string()
                    },
                    "".to_string().into()
                );
                return Ok(Response::builder(200)
                    .body(Body::from_json(&response)?)
                    .content_type("application/json")
                    .build());
            }

            let keys: Vec<Vec<u8>>;
            let hex_keys: Vec<String>;
            let table;

            if method == "getTransactionByHash" || method == "getTransactions" {
                table = "s_hash_2_tx:".as_bytes();
            } else if method == "getTransactionReceipt" || method == "getTransactionReceipts" {
                table = "s_hash_2_receipt:".as_bytes();
            } else {
                let response = new_json_response!(
                    request_json.id,
                    JsonError {
                        message: "method not found".to_string(),
                        code: -1001,
                        data: "".to_string()
                    },
                    "".to_string().into()
                );
                return Ok(Response::builder(200)
                    .body(Body::from_json(&response)?)
                    .content_type("application/json")
                    .build());
            };
            if method == "getTransactionByHash" || method == "getTransactionReceipt" {
                let hash = request_json.params.as_array().unwrap()[0].as_str().unwrap();
                hex_keys = vec![hash.to_string()];
                keys = vec![table
                    .to_vec()
                    .into_iter()
                    .chain(
                        prefix_hex::decode::<Vec<u8>>(hash)
                            .unwrap()
                            .to_owned()
                            .into_iter(),
                    )
                    .collect()];
            } else {
                hex_keys = request_json.params.as_array().unwrap()[0]
                    .as_array()
                    .unwrap()
                    .into_iter()
                    .map(|s| s.as_str().unwrap().to_string())
                    .collect();
                debug!("hex_keys: {:?}", hex_keys);
                keys = hex_keys
                    .iter()
                    .map(|s| {
                        table
                            .to_vec()
                            .into_iter()
                            .chain(
                                prefix_hex::decode::<Vec<u8>>(s.as_str())
                                    .unwrap()
                                    .to_owned()
                                    .into_iter(),
                            )
                            .collect()
                    })
                    .collect();
                debug!("keys: {:?}", keys);
            }

            // #[allow(unused_assignments)]
            let result: HashMap<String, String>;
            let database = req.state().lock().await;
            match &*database {
                Storage::RocksDB(db) => {
                    let values: Vec<String> = db
                        .multi_get(keys)
                        .into_iter()
                        .filter_map(|x| x.ok())
                        .map(|x| match x {
                            Some(v) => unsafe {
                                std::str::from_utf8_unchecked(v.as_ref()).to_string()
                            },
                            None => "".to_string(),
                        })
                        .collect();
                    // debug!("values: {:?}", values);
                    result = zip(hex_keys, values)
                        .map(|(k, v)| (k.to_string(), v))
                        .collect();
                }
                Storage::TiKV(_client) => {
                    let client = _client.clone();
                    let timestamp = client.current_timestamp().await?;
                    let mut txn = client.snapshot(timestamp, TransactionOptions::new_optimistic());
                    result = txn
                        .batch_get(keys)
                        .await?
                        .map(|x| unsafe {
                            let value = std::str::from_utf8_unchecked(x.value()).to_string();
                            let key: Vec<u8> = x.key().clone().into();
                            let key = prefix_hex::encode(key.strip_prefix(table).unwrap());
                            (key, value)
                        })
                        .collect();
                }
            }
            let response = JsonResponse {
                jsonrpc: "2.0".to_string(),
                id: request_json.id,
                result: serde_json::to_value(result).unwrap(),
                error: JsonError {
                    message: "".to_string(),
                    code: 0,
                    data: "".to_string(),
                },
            };
            Ok(Response::builder(200)
                .body(Body::from_json(&response)?)
                .content_type("application/json")
                .build())
        });
    app.listen(args.ip_port).await?;
    Ok(())
}

// #[cfg(test)]
// mod tests {
//     use super::*;
// }
