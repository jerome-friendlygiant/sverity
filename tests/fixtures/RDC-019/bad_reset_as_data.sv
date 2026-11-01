// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: y <= d & rst_n; => y <= d;
// revert-expect-none: RDC-019 WARNING
// expect: RDC-019 WARNING
module top(input logic clka, rst_n, d, output logic q, y);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka) y <= d & rst_n;
endmodule
