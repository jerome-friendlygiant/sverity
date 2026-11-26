// sverity-args: --top top --sdc @DIR@/wild.sdc
// The exception reaches the reset through a wildcard pattern (rst*), which the constraint reader must expand. The rule has no Unknown outcome: the pattern resolves to rst_n, an asynchronous reset root, so the exception is still reported as one WARNING.
// expect-count: RDC-027 WARNING 1
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
