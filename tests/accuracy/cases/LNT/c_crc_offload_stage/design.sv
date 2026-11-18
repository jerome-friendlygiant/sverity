// CRC accumulation stage for a word stream. The per-word update is computed
// by a library routine linked into the simulation image.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        init_i,
  input  logic        valid_i,
  input  logic [31:0] word_i,
  output logic [31:0] crc_o,
  output logic [15:0] words_o
);
  import "DPI-C" function int unsigned crc32_step(input int unsigned crc,
                                                  input int unsigned word);

  logic [31:0] crc_q;
  logic [31:0] crc_d;
  logic [15:0] word_cnt;

  always_comb begin
    crc_d = crc32_step(crc_q, word_i);
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      crc_q    <= 32'hffff_ffff;
      word_cnt <= 16'd0;
    end else if (init_i) begin
      crc_q    <= 32'hffff_ffff;
      word_cnt <= 16'd0;
    end else if (valid_i) begin
      crc_q    <= crc_d;
      word_cnt <= word_cnt + 16'd1;
    end
  end

  assign crc_o   = ~crc_q;
  assign words_o = word_cnt;
endmodule
