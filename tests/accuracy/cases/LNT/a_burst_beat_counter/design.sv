// Beat counter for a DMA burst: raises done on the last beat of the programmed length.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       start,
  input  logic       beat,
  input  logic [3:0] len_m1,
  output logic       busy,
  output logic       done
);
  logic [3:0] beat_q;
  logic       busy_q;
  logic       done_q;

  assign beat_last = (beat_q == len_m1);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      beat_q <= 4'd0;
      busy_q <= 1'b0;
      done_q <= 1'b0;
    end else begin
      done_q <= 1'b0;
      if (start && !busy_q) begin
        busy_q <= 1'b1;
        beat_q <= 4'd0;
      end else if (busy_q && beat) begin
        if (beat_last) begin
          busy_q <= 1'b0;
          done_q <= 1'b1;
        end else begin
          beat_q <= beat_q + 4'd1;
        end
      end
    end
  end

  assign busy = busy_q;
  assign done = done_q;
endmodule
