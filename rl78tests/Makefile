# コンパイラ設定（オプションはダミー）
CCRL = ccrl
CCRLFLAGS = -cpu=S3 -lnkopt=-AUTO_SECTION_LAYOUT -lnkopt=-DEVICE=DR7F100GLG.DVF -lnkopt=-ROM=.data=.data_R -lnkopt=-ROM=.sdata=.sdata_R -lnkopt=-MEMORY=High

# prelude.asmを除くすべてのasmファイルを検出
ASM_FILES := $(filter-out prelude.asm,$(wildcard *.asm))

# 出力ディレクトリ
OUT_DIR = out

# 各asmファイルから実行可能ファイル名を生成（outディレクトリ内）
ABS_FILES := $(addprefix $(OUT_DIR)/,$(ASM_FILES:.asm=.abs))

# デフォルトターゲット
all: $(ABS_FILES)

# outディレクトリを作成
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# 各asmファイルから実行可能ファイルをビルドするルール
$(OUT_DIR)/%.abs: %.asm | $(OUT_DIR)
	$(CCRL) $(CCRLFLAGS) -obj_path=$(OUT_DIR) -lnkopt=-OUTPUT=$@ $<

# クリーンアップ
clean:
	rm -rf $(OUT_DIR)

.PHONY: all clean

