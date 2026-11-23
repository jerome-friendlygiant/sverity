// Mode-exclusive clock handoff (only one clock runs at a time).
module top (
  input  logic clk_fast,
  input  logic clk_slow,
  input  logic slow_rst_ni,
  input  logic d_i,
  output logic q_o
);
  logic hand_q;
  logic cap_q;

  always_ff @(posedge clk_fast) hand_q <= d_i;

  always_ff @(posedge clk_slow) begin
    if (!slow_rst_ni) cap_q <= 1'b0;
    else              cap_q <= hand_q;
  end

  assign q_o = cap_q;
endmodule
