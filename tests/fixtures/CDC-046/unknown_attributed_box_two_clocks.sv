// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: bridge u_bridge(.ca(clka), .cb(clkb), => bridge u_bridge(.ca(clka), .cb(clka),
// revert-expect-none: CDC-046
// A module attributed as a black box takes two asynchronous clocks: a crossing inside it is not analysed.
// expect: CDC-046 WARNING 11
// expect: CST-005 WARNING 11
// expect-coverage: blackboxes INCOMPLETE
(* black_box *) module bridge(input wire ca, cb, input wire [7:0] a, output wire [7:0] b);
endmodule
module top(input wire clka, clkb, input wire [7:0] din, output wire [7:0] dout);
  bridge u_bridge(.ca(clka), .cb(clkb), .a(din), .b(dout));
endmodule
