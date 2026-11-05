// sverity-args: --top top
// A tied-off port, a constant net, literal padding and a loop index all have a known source: none is "undriven".
// expect-none: CST-010
module child(input logic [1:0] tie, input logic [1:0] mixed, output logic [1:0] o);
  assign o = tie ^ mixed;
endmodule
module top(input logic clk, input logic [1:0] d, output logic [1:0] q, output logic r);
  logic [1:0] o, fixed;
  assign fixed = 2'b01;
  child u(.tie(2'b10), .mixed({d[0] ^ d[1], 1'b0}), .o(o));
  always_ff @(posedge clk) q <= o ^ fixed;
  always_ff @(posedge clk) begin
    r <= 1'b0;
    for (int i = 0; i < 2; i++) if (d[i]) r <= 1'b1;
  end
endmodule
