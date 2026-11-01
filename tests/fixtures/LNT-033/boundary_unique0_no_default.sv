// sverity-args: --top top
// expect-none: LNT-033
module top(input bit [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique0 case (s)
      2'd0: y = 1'b1;
    endcase
  end
endmodule
