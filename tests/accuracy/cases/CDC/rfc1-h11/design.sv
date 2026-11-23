// Payload transfer qualified by a synchronized valid.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       vld_i,
  input  logic [7:0] pay_i,
  input  logic       force_i,
  output logic [7:0] cap_o
);
  logic       vld_q;
  logic [7:0] pay_q;
  logic       v1_q;
  logic       v2_q;
  logic [7:0] cap_q;

  always_ff @(posedge clk_a) begin
    vld_q <= vld_i;
    pay_q <= pay_i;
  end

  always_ff @(posedge clk_b) begin
    v1_q <= vld_q;
    v2_q <= v1_q;
  end

  always_ff @(posedge clk_b) begin
    if (v2_q) cap_q <= pay_q;
    else if (force_i) cap_q <= pay_q;
  end

  assign cap_o = cap_q;
endmodule
