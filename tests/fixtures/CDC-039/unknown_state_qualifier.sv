// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-039 WARNING
// expect-none: CDC-003 ERROR
// expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, launch, value, accept, output logic result);
  logic payload, request, s1, s2;
  typedef enum logic {WAITING, AVAILABLE} state_t;
  state_t state;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin request <= 0; payload <= 0; end
    else begin request <= launch; if (launch) payload <= value; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= 0; s2 <= 0; state <= WAITING; result <= 0; end
    else begin
      s1 <= request; s2 <= s1;
      case (state)
        WAITING: if (s2) state <= AVAILABLE;
        AVAILABLE: if (accept) state <= WAITING;
      endcase
      if ((state == AVAILABLE) && accept) result <= payload;
    end
endmodule
