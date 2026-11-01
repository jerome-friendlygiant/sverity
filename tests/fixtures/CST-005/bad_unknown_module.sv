// sverity-args: --top top --ignore-unknown-modules
// An unknown module is a black box: inventoried, UNMODELED, coverage INCOMPLETE.
// expect: CST-005 WARNING
// expect-coverage: blackboxes INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  mystery_ip u_ip(.clk(clka), .d(d), .q(q));
endmodule
