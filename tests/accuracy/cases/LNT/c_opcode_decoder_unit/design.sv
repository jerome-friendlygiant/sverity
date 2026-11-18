// Operation decoder for a small ALU. The two-bit opcode selects the
// function; the decoded operation is applied to two 8-bit operands.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [1:0] op,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] result
);
  logic [2:0] alu_op;
  logic [7:0] res_c;

  always_comb begin
    alu_op = 3'd0;
    case (op) // synopsys full_case
      2'b00: alu_op = 3'd1;
      2'b01: alu_op = 3'd2;
      2'b10: alu_op = 3'd4;
    endcase
  end

  always_comb begin
    case (alu_op)
      3'd1:    res_c = a + b;
      3'd2:    res_c = a & b;
      3'd4:    res_c = a ^ b;
      default: res_c = 8'd0;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) result <= 8'd0;
    else        result <= res_c;
  end
endmodule
