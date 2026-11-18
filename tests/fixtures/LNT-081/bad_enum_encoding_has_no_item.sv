// sverity-args: --top top
// Three enumerators in two bits, all listed: the fourth encoding is a value the hardware can hold, and no item matches it.
// expect: LNT-081 WARNING 8
typedef enum logic [1:0] {A = 2'd0, B = 2'd1, C = 2'd2} st_t;
module top(input st_t s, input logic a, b, c, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s)
      A: y = a;
      B: y = b;
      C: y = c;
    endcase
  end
endmodule
