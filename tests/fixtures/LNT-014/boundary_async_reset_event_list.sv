// sverity-args: --top top
// Near-miss of bad_always_ff_no_event: one event control with two edges is still exactly one event control.
// expect-none: LNT-014
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= d;
endmodule
