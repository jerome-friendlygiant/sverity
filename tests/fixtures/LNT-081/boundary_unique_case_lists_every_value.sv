// sverity-args: --top top
// Every two-state value of the selector has an item; only X/Z of a four-state selector matches none, which a simulation check reports
// and no hardware holds.
// expect: LNT-081 INFO 9
// expect-none: LNT-081 WARNING
module top(input logic [1:0] s, input logic a, b, c, d, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s)
      2'd0: y = a;
      2'd1: y = b;
      2'd2: y = c;
      2'd3: y = d;
    endcase
  end
endmodule
