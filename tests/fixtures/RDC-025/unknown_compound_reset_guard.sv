// sverity-args: --top top
// The reset guard is compound (!rst_n && mode): with mode==0, rst_n falling captures d instead of clearing q,
// so the forced value is undecided ('?') and RDC-025 is Unresolved (round 4 A2a).
// expect-unresolved: RDC-025
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, rst_n, mode, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n && mode) q <= 1'b0;
    else q <= d;
endmodule
