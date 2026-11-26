// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An async reset sampled as data by an ordinary single-stage register (not a 2-stage data synchroniser) must still report RDC-019 WARNING.
// revert: always_ff @(posedge clka) flag_q <= ~rst_n; => always_ff @(posedge clka) flag_q <= d;
// revert-expect-none: RDC-019 WARNING
// expect: RDC-019 WARNING
module top(input logic clka, rst_n, d, output logic q, flag_q);
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= d;
  always_ff @(posedge clka) flag_q <= ~rst_n;
endmodule
