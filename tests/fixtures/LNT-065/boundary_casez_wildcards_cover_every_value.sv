// sverity-args: --top top
// The four wildcard items match all 16 values of the selector: nothing is left without an arm, so the missing default is style.
// expect: LNT-065 INFO 8
// expect-none: LNT-065 WARNING
module top(input logic [3:0] op, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    casez (op)
      4'b00??: y = 2'd0;
      4'b01??: y = 2'd1;
      4'b10??: y = 2'd2;
      4'b11??: y = 2'd3;
    endcase
  end
endmodule
