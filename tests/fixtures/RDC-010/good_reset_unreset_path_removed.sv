// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset register has no dependency on the unreset register.
// expect-none: RDC-010
module top(input logic clka, rst_n, d, output logic q);
  logic u;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka) u <= d;
endmodule
