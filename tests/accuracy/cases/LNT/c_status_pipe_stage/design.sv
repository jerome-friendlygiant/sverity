// Two-stage status pipeline: stage one registers the raw flags, stage two
// accumulates them into sticky bits that the register interface reads.
module top #(
  parameter time TPD = 0
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] flags_i,
  input  logic       clr_i,
  output logic [3:0] summary_o,
  output logic       any_o
);
  logic [3:0] flags_q;
  logic [3:0] sticky_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      flags_q  <= 4'd0;
      sticky_q <= 4'd0;
    end else begin
      flags_q  <= flags_i;
      sticky_q <= clr_i ? 4'd0 : (sticky_q | flags_q);
    end
  end

  assign #(TPD) summary_o = sticky_q;
  assign any_o = |sticky_q;
endmodule
