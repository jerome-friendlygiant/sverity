// Event accumulator that runs from one of two clock sources. The selection
// input chooses the source feeding the accumulator domain.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_n,
  input  logic       clk_sel,
  input  logic       pulse_i,
  output logic [7:0] acc_o
);
  logic       core_clk;
  logic [7:0] acc_q;

  assign core_clk = clk_sel ? clk_b : clk_a;

  always_ff @(posedge core_clk or negedge rst_n) begin
    if (!rst_n)       acc_q <= 8'd0;
    else if (pulse_i) acc_q <= acc_q + 8'd1;
  end

  assign acc_o = acc_q;
endmodule
