// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: posedge rst_n) if (rst_n) => negedge rst_n) if (!rst_n)
// revert-expect-none: RDC-003 WARNING
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic first, last;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) first <= 0; else first <= 1;
  always_ff @(posedge clka or posedge rst_n) if (rst_n) last <= 0; else last <= first;
  always_ff @(posedge clka or negedge last) if (!last) q <= 0; else q <= d;
endmodule
