
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd $dirpath

bash clear_tikv.sh
bash start_tikv.sh
