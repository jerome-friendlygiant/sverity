// sverity-args: --top top
// The same module twice: its task is one declaration, so one finding.
// expect: LNT-013 WARNING 6
// expect-count: LNT-013 WARNING 1
module lane(input logic [7:0] d, input logic en, output logic [7:0] q);
  task automatic gate(input logic [7:0] v, input logic e, output logic [7:0] o);
    if (e) o = v;
  endtask
  always_comb gate(d, en, q);
endmodule
module top(input logic [15:0] d, input logic [1:0] en, output logic [15:0] q);
  lane u0(.d(d[7:0]), .en(en[0]), .q(q[7:0]));
  lane u1(.d(d[15:8]), .en(en[1]), .q(q[15:8]));
endmodule
