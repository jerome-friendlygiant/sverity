// sverity-args: --top top
// The same casez without the 11?? item: four selector values are left without an arm.
// expect: LNT-065 WARNING 7
module top(input logic [3:0] op, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    casez (op)
      4'b00??: y = 2'd0;
      4'b01??: y = 2'd1;
      4'b10??: y = 2'd2;
    endcase
  end
endmodule
