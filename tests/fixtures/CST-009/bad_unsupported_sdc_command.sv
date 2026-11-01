// sverity-args: --top top --sdc @DIR@/unsup.sdc
// An SDC command SVerity does not interpret is disclosed, never silently dropped.
// expect: CST-009 WARNING
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
