// Error limiter for a control loop: reports the sign of the error and clips
// it to +/-2000 counts so the downstream gain stage cannot overflow.
module top (
  input  logic               clk,
  input  logic               rst_n,
  input  logic signed [11:0] err_i,
  output logic signed [11:0] err_clip_o,
  output logic               neg_o,
  output logic               clipped_o
);
  localparam logic signed [11:0] LIM = 12'sd2000;

  logic signed [11:0] clip_c;
  logic               hit_c;

  always_comb begin
    hit_c = 1'b0;
    clip_c = err_i;
    if (err_i > LIM) begin
      clip_c = LIM;
      hit_c  = 1'b1;
    end else if (err_i < -LIM) begin
      clip_c = -LIM;
      hit_c  = 1'b1;
    end
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      err_clip_o <= 12'sd0;
      neg_o      <= 1'b0;
      clipped_o  <= 1'b0;
    end else begin
      err_clip_o <= clip_c;
      neg_o      <= (err_i < 12'sd0);
      clipped_o  <= hit_c;
    end
  end
endmodule
