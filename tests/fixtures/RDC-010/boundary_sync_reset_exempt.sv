// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Edge of the trigger: u has no async reset but a synchronous reset candidate (constant-only branch), so it is exempt even though it feeds the async-reset register q.
// expect-none: RDC-010
module top(input logic clka, srst_n, rst_n, d, output logic q);
  logic u;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= u;
  always_ff @(posedge clka) if (!srst_n) u <= 1'b0; else u <= d;
endmodule
