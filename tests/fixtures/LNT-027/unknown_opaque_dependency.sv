// sverity-args: --top top
// expect: LNT-027 WARNING
// expect-coverage: blackboxes INCOMPLETE
(* blackbox *) module opaque(input logic c, output logic y); endmodule
module top(input logic clk,d, output logic q, observed);
always @(posedge clk) q <= d;
opaque u(clk,observed);
endmodule
