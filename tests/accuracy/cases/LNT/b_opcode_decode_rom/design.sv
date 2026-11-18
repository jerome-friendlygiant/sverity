// One-hot control word decoded from a 4-bit opcode.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] opcode,
  input  logic       op_valid,
  output logic [7:0] ctrl_q
);
  logic [7:0] ctrl;

  always_comb begin
    case (opcode)
      4'h0:    ctrl = 8'h01;
      4'h1:    ctrl = 8'h02;
      4'h2:    ctrl = 8'h04;
      4'h3:    ctrl = 8'h08;
      4'h4:    ctrl = 8'h10;
      4'h3:    ctrl = 8'h20;
      4'h6:    ctrl = 8'h40;
      4'h7:    ctrl = 8'h80;
      default: ctrl = 8'h00;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)        ctrl_q <= 8'h00;
    else if (op_valid) ctrl_q <= ctrl;
    else               ctrl_q <= 8'h00;
  end
endmodule
