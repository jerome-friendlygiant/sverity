// sverity-args: --top top
// expect: LNT-072 WARNING
// expect-coverage: blackboxes INCOMPLETE
(* blackbox *) module opaque(input logic c, output logic y); endmodule
module top(input logic clk,rst_n,d, output logic q,r);
logic generated_clk;
opaque u(rst_n,generated_clk);
always @(posedge clk or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
always @(posedge generated_clk) r <= d;
endmodule
