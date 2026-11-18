// Double-data-rate input capture: one bit is sampled on each clock edge and
// the pair is presented as a 2-bit word with a valid flag.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       d_in,
  input  logic       en,
  output logic [1:0] word_o,
  output logic       word_vld
);
  logic q_rise;
  logic q_fall;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q_rise <= 1'b0;
    else if (en) q_rise <= d_in;
  end

  always_ff @(negedge clk or negedge rst_n) begin
    if (!rst_n) q_fall <= 1'b0;
    else if (en) q_fall <= d_in;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) word_vld <= 1'b0;
    else        word_vld <= en;
  end

  assign word_o = {q_fall, q_rise};
endmodule
