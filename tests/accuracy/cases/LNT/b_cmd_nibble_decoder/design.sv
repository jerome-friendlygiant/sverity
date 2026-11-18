// Lane enable decode for a 4-lane transmitter, registered at the output.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] cmd,
  output logic [3:0] lane_en_q
);
  logic [3:0] lane_en;

  always_comb begin
    case (cmd)
      4'h1:    lane_en = 4'b0001;
      4'h2:    lane_en = 4'b0010;
      4'h4:    lane_en = 4'b0100;
      4'h8:    lane_en = 4'b1000;
      4'hF:    lane_en = 4'b1111;
      default: lane_en = 4'bxxxx;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) lane_en_q <= 4'b0000;
    else        lane_en_q <= lane_en;
  end
endmodule
