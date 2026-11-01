// sverity-args: --top top --sdc @DIR@/missing_port.sdc
// A clock constraint that names no design object must be reported, not ignored.
// expect: CST-002 WARNING
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
