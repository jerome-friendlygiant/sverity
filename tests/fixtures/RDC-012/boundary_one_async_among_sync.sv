// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary: one asynchronous assertion source (rst_n) beside a synchronous-only reset (rst) yields exactly one RDC-012 diagnostic for the design, since the trigger needs only one async source.
// expect-count: RDC-012 WARNING 1
module top(input logic clka, rst, rst_n, d, output logic q, p);
  always_ff @(posedge clka) if (rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) p <= 1'b0; else p <= d;
endmodule
