// 7-bit LFSR with optional seed load.
module lfsr7 (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic       load,
  input  logic [6:0] seed,
  output logic [6:0] q,
  output logic       bit_out
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)     q <= 7'h7F;
    else if (load)  q <= seed;
    else if (en)    q <= {q[5:0], q[6] ^ q[5]};
  end
  assign bit_out = q[6];
endmodule

// Scrambler wrapper.
//   seed_valid  qualifies seed_load and seed from the configuration block
//   seed_load   load strobe from the configuration block
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic       seed_valid,
  input  logic       seed_load,
  input  logic [6:0] seed,
  output logic [6:0] state,
  output logic       scr_bit
);
  lfsr7 u_lfsr (
    .clk(clk), .rst_n(rst_n), .en(en),
    .load(seed_valid ? seed_load : 1'bz),
    .seed(seed),
    .q(state), .bit_out(scr_bit)
  );
endmodule
