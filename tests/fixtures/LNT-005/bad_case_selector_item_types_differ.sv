// sverity-args: --top top
// The item is a 2-bit enum and the selector a 3-bit vector: the widths differ, so the comparison is not bit for bit.
// expect: LNT-005 WARNING 8
module top(input logic [2:0] s, output logic y);
  typedef enum logic [1:0] {A = 2'b00, B = 2'b01} e_t;
  always_comb begin
    case (s)
      A: y = 1'b0;
      default: y = 1'b1;
    endcase
  end
endmodule
