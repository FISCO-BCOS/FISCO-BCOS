var fs = require('fs');
var execSync = require('child_process').execSync;

var options = process.argv;
if (options.length < 3) {
  console.log('Usage: node init.js node0.sample node1.sample node2.sample... ');
  process.exit(0);
}
if (options.length > 6) {
  console.log('Max Four Node! ');
  process.exit(0);
}
var nodeconfiglength = 0;
var nodeconfig = [];
while (nodeconfig.length < options.length - 2) {
  var node = JSON.parse(fs.readFileSync(options[2 + nodeconfig.length], 'utf-8'));
  nodeconfig.push(node);
}

console.log("开始检查配置...");
var check = true;
for (var i = 0; i < nodeconfig.length; i++) {
  for (var j = 0; j < nodeconfig.length; j++) {
    if (i != j) {
      if (nodeconfig[i].networkid != nodeconfig[j].networkid) {
        check = false;
        console.log('节点' + i + '和节点' + j + ' networkid不一致，请检查!');
      }
      if (nodeconfig[i].ip == nodeconfig[j].ip) {
        if (nodeconfig[i].nodedir == nodeconfig[j].nodedir) {
          console.log('节点' + i + ',和节点' + j + ' IP相同且datadir相同，请检查!');
          check = false;
        }
        if (nodeconfig[i].rpcport == nodeconfig[j].rpcport) {
          console.log('节点' + i + ',和节点' + j + ' IP相同且rpcport相同，请检查!');
          check = false;
        }
        if (nodeconfig[i].p2pport == nodeconfig[j].p2pport) {
          console.log('节点' + i + ',和节点' + j + ' IP相同且p2pport相同，请检查!');
          check = false;
        }
      }
    }
  }
}
if (!check) {
  process.exit(1);
} else {
  console.log('配置检查成功!!!');
}

var admin;
try {
  admin = execSync("node ../tool/accountManager.js");
  console.log('生成管理员账户成功!!!即将拷贝备份到每个节点目录的admin.message文件中，请注意保管!!!');
} catch (e) {
  console.log('管理员账户生成失败!!!');
  process.exit(1);
}

var nodeid = [];
console.log('开始初始化节点配置...');
for (var i = 0; i < nodeconfig.length; i++) {
  try {
    execSync("mkdir -p " + nodeconfig[i].nodedir);
    execSync("mkdir -p " + nodeconfig[i].nodedir + "log/");
    execSync("mkdir -p " + nodeconfig[i].nodedir + "data/");

    execSync("cp " + "./cert/fisco/" + i + "/*   " + nodeconfig[i].nodedir + "data/");
    var id = fs.readFileSync(nodeconfig[i].nodedir + "data/node.nodeid", 'utf-8');
    id = id.replace(/\n/g, "");
    nodeid.push(id);

    var logconfig = fs.readFileSync("log.conf.template", 'utf-8');
    fs.writeFileSync(nodeconfig[i].nodedir + 'log.conf', logconfig.replace(/{nodedir}/g, nodeconfig[i].nodedir));

    var startsh = fs.readFileSync("start.sh.template", 'utf-8');
    startsh = startsh.replace(/{genesis}/g, nodeconfig[i].nodedir + 'genesis.json');
    fs.writeFileSync(nodeconfig[i].nodedir + 'start' + i + '.sh', startsh.replace(/{config}/g, nodeconfig[i].nodedir + 'config.json'));
    execSync("chmod -x " + nodeconfig[i].nodedir + 'start' + i + '.sh');

    fs.writeFileSync(nodeconfig[i].nodedir + 'admin.message', admin);
    console.log('节点' + i + '目录生成成功!!!');
  } catch (e) {
    console.log('节点' + i + '目录生成失败!' + e);
  }

} //for

admin = fs.readFileSync(nodeconfig[0].nodedir + 'admin.message', 'utf-8');
admin = (admin.split(/\n/)[2].split(/ /)[2]);

var genesis = fs.readFileSync("genesis.json.template", 'utf-8');
genesis = genesis.replace(/{initMinerNodes}/g, JSON.stringify(nodeid));
genesis = genesis.replace(/{admin}/g, admin);
console.log('创世文件生成成功!!!');



for (var i = 0; i < nodeconfig.length; i++) {
  try {
    fs.writeFileSync(nodeconfig[i].nodedir + 'genesis.json', genesis);
    console.log('节点' + i + ' genesis.json生成成功!!!');

    var config = fs.readFileSync("config.json.template", 'utf-8');
    config = config.replace(/{nodedir}/g, nodeconfig[i].nodedir);
    config = config.replace(/{networkid}/g, nodeconfig[i].networkid);
    config = config.replace(/{ip}/g, nodeconfig[i].ip);
    config = config.replace(/{rpcport}/g, nodeconfig[i].rpcport);
    config = config.replace(/{p2pport}/g, nodeconfig[i].p2pport);
    config = config.replace(/{channelPort}/g, nodeconfig[i].channelPort);
    fs.writeFileSync(nodeconfig[i].nodedir + 'config.json', config);
    console.log('节点' + i + ' config.json生成成功!!!');

    var booststrap = fs.readFileSync("bootstrapnodes.json.sample", 'utf-8');
    booststrap = booststrap.replace(/{host}/g, nodeconfig[0].ip);
    booststrap = booststrap.replace(/{p2pport}/g, nodeconfig[0].p2pport);
    fs.writeFileSync(nodeconfig[i].nodedir + 'data/bootstrapnodes.json', booststrap);
    console.log('节点' + i + ' bootstrapnodes.json生成成功!!!');

  } catch (e) {
    console.log('节点' + i + ' 配置生成失败!' + e);
  }

} //for