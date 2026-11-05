// sverity-args: --top top --sdc @DIR@/reset_exception.sdc
// The reset root is named by the exception, so the RDC-002 unknown-source trigger is exempt.
// expect-none: RDC-002
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
