module top(input logic [2:0] opcode,
  output logic write_en,
  output logic [1:0] bank);

  typedef struct packed {logic wr; logic [1:0] dest;} decoded_t;
  decoded_t decoded;
  always_comb begin
    case (opcode)
      3'd1: decoded = '{wr:1'b1, dest:2'd0};
      3'd2: decoded = '{wr:1'b1, dest:2'd1};
      3'd5: decoded = '{wr:1'b1, dest:2'd3};
      default: decoded = '0;
    endcase
  end
  assign {write_en, bank} = decoded;

endmodule
