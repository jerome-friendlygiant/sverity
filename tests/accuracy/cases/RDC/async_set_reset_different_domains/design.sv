module top (
  input  logic clk_a_i,
  input  logic clk_b_i,
  input  logic reset_a_ni,
  input  logic event_b_i,
  input  logic data_a_i,
  output logic flag_o
);
  logic event_b_q;
  logic flag_q;

  always_ff @(posedge clk_b_i or negedge reset_a_ni) begin
    if (!reset_a_ni) event_b_q <= 1'b0;
    else event_b_q <= event_b_i;
  end

  // A set from clk_b_i and a reset from another domain share one flop.
  always_ff @(posedge clk_a_i or posedge event_b_q or negedge reset_a_ni) begin
    if (!reset_a_ni) flag_q <= 1'b0;
    else if (event_b_q) flag_q <= 1'b1;
    else flag_q <= data_a_i;
  end

  assign flag_o = flag_q;
endmodule
