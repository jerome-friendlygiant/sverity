// Small ALU slice: add, subtract, and, xor with carry and zero flags.
module top (
  input  logic [1:0] op,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] res,
  output logic       carry,
  output logic       zero
);
  logic [8:0] wide;

  always_comb begin
    case (op)
      2'd0: begin
        wide  = {1'b0, a} + {1'b0, b};
        res   = wide[7:0];
        carry = wide[8];
      end
      2'd1: begin
        wide  = {1'b0, a} - {1'b0, b};
        res   = wide[7:0];
        carry = wide[8];
      end
      2'd2: begin
        wide  = {1'b0, a & b};
        res   = wide[7:0];
      end
      default: begin
        wide  = {1'b0, a ^ b};
        res   = wide[7:0];
        carry = 1'b0;
      end
    endcase
  end

  assign zero = (res == 8'd0);
endmodule
