// Over-temperature detector: trips after three consecutive readings above the limit.
module top (
  input  logic              clk,
  input  logic              rst_n,
  input  logic              sample_valid,
  input  logic signed [7:0] temp_c,
  input  logic [7:0]        limit_c,
  output logic              over_o
);
  logic [1:0] hits_q;
  logic       over_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      hits_q <= 2'd0;
      over_q <= 1'b0;
    end else if (sample_valid) begin
      if (temp_c > limit_c) begin
        if (hits_q == 2'd2) over_q <= 1'b1;
        else                hits_q <= hits_q + 2'd1;
      end else begin
        hits_q <= 2'd0;
        over_q <= 1'b0;
      end
    end
  end

  assign over_o = over_q;
endmodule
