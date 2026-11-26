// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary: two registers share one asynchronous reset source (rst_n), so the one-per-source rule gives exactly one RDC-013 finding, not one per register.
// expect-count: RDC-013 WARNING 1
module top(input logic clka, rst_n, d, output logic q, p);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) p <= 1'b0; else p <= q;
endmodule
