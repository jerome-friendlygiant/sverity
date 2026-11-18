// Thermal trip comparator with a registered result.
module top (
  input  logic              clk,
  input  logic              rst_n,
  input  logic signed [7:0] temp_c,
  input  logic [7:0]        limit_c,
  output logic              trip_o,
  output logic              hot_o
);
  logic signed [8:0] limit_s;
  logic              trip_q;
  logic              hot_q;

  assign limit_s = {1'b0, limit_c};

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      trip_q <= 1'b0;
      hot_q  <= 1'b0;
    end else begin
      trip_q <= (temp_c > limit_s);
      hot_q  <= trip_q && (temp_c > limit_s);
    end
  end

  assign hot_o = hot_q;

  assign trip_o = trip_q;
endmodule
