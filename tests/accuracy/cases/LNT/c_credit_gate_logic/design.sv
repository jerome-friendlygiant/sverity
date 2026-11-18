// Credit-based sender gate. Credits are returned by the receiver and spent
// one per transmitted beat; a beat may be sent while a request is pending.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       req,
  input  logic       credit_ret,
  output logic       send,
  output logic [3:0] credits_o
);
  logic [3:0] credits;
  logic       can_send;

  assign can_send = req && (credits >= 4'd0);
  assign send     = can_send && (credits != 4'd0);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      credits <= 4'd8;
    end else begin
      case ({send, credit_ret})
        2'b10:   credits <= credits - 4'd1;
        2'b01:   credits <= (credits == 4'd15) ? credits : credits + 4'd1;
        default: credits <= credits;
      endcase
    end
  end

  assign credits_o = credits;
endmodule
