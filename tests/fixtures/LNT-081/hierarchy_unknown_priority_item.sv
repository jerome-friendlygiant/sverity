// sverity-args: --top top
// expect: LNT-081 WARNING 7
// expect-none: LNT-033
module top(input logic [1:0] s, input logic [1:0] k, output logic y);
  always_comb begin
    y = 1'b0;
    priority case (s)
      k: y = 1'b1;
    endcase
  end
endmodule
