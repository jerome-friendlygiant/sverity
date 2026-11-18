// Free-running timestamp counter. Each event latches the current count
// into a capture register and raises a one-cycle valid flag.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        event_i,
  output logic [15:0] stamp_o,
  output logic        stamp_vld
);
  logic [15:0] free_cnt;

  // Time base.
  always_ff @(posedge clk or negedge rst_n) begin
    if (rst_n) begin
      free_cnt <= 16'd0;
    end else begin
      free_cnt <= free_cnt + 16'd1;
    end
  end

  // Capture register.
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      stamp_o   <= 16'd0;
      stamp_vld <= 1'b0;
    end else begin
      stamp_vld <= event_i;
      if (event_i) stamp_o <= free_cnt;
    end
  end
endmodule
