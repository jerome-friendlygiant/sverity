// Priority encoder for four interrupt lines, registered at the output.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] irq_in,
  input  logic       irq_en,
  output logic [1:0] vec,
  output logic       pending
);
  logic [1:0] vec_c;
  logic       pend_c;

  always_comb begin
    vec_c  = 2'd0;
    pend_c = 1'b1;
    casex (irq_in)
      4'b1xxx: vec_c = 2'd3;
      4'b01xx: vec_c = 2'd2;
      4'b001x: vec_c = 2'd1;
      4'b0001: vec_c = 2'd0;
      default: pend_c = 1'b0;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      vec     <= 2'd0;
      pending <= 1'b0;
    end else begin
      vec     <= vec_c;
      pending <= pend_c & irq_en;
    end
  end
endmodule
