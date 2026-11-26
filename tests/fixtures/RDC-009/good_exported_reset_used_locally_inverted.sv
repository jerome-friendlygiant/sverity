// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The output reset is also used by a local asynchronous pin through an inversion: it is analysed as the internal root (one finding, on rl), not reported as exported.
// expect: RDC-009 WARNING
// expect-count: RDC-009 WARNING 1
module top(input logic clka, rst_n, d, output logic q, rst_o);
  logic en;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) en <= 1'b0; else en <= d;
  assign rst_o = rst_n & en;
  wire rl = ~rst_o;
  always_ff @(posedge clka or posedge rl) if (rl) q <= 1'b0; else q <= d;
endmodule
