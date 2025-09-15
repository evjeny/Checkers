#pragma once
#include <random>
#include <vector>

#include "../Models/Move.h"
#include "Board.h"
#include "Config.h"

const int INF = 1e9;

class Logic
{
  public:
    Logic(Board *board, Config *config) : board(board), config(config)
    {
        rand_eng = std::default_random_engine (
            !((*config)("Bot", "NoRandom")) ? unsigned(time(0)) : 0);
        scoring_mode = (*config)("Bot", "BotScoringType");
        optimization = (*config)("Bot", "Optimization");
    }

    vector<move_pos> find_best_turns(const bool color)
    {
        // Сбрасываем внутренние структуры, но используем их иначе
        next_move.clear();
        next_best_state.clear();

        // Гарантируем наличие корневого состояния
        next_best_state.push_back(-1);
        next_move.emplace_back(-1, -1, -1, -1);

        // Стартуем подбор лучшей линии хода для текущего игрока
        // В качестве "корня" передаём текущую доску и state = 0
        find_first_best_turn(board->get_board(), color, -1, -1, /*state=*/0, /*alpha=*/-1.0);

        // Восстанавливаем найденную линию ходов из next_*
        vector<move_pos> line;
        int st = 0;
        while (st != -1 && st < (int)next_move.size())
        {
            const auto mv = next_move[st];
            if (mv.x == -1) break;
            line.push_back(mv);
            st = next_best_state[st];
        }
        return line;
    }

private:
    vector<vector<POS_T>> make_turn(vector<vector<POS_T>> mtx, move_pos turn) const
    {
        // убираем побитую шашку
        if (turn.xb != -1)
            mtx[turn.xb][turn.yb] = 0;
        // превращаем шашку в дамку
        if ((mtx[turn.x][turn.y] == 1 && turn.x2 == 0) || (mtx[turn.x][turn.y] == 2 && turn.x2 == 7))
            mtx[turn.x][turn.y] += 2;
		// передвигаем шашку на новое место
        mtx[turn.x2][turn.y2] = mtx[turn.x][turn.y];
		// очищаем старую позицию
        mtx[turn.x][turn.y] = 0;
        return mtx;
    }

    // подсчет очков бота для оценки текущей расстановки
    double calc_score(const vector<vector<POS_T>> &mtx, const bool first_bot_color) const
    {
        // color - who is max player
        double w = 0, wq = 0, b = 0, bq = 0;
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                w += (mtx[i][j] == 1); // пешка белых
				wq += (mtx[i][j] == 3); // дамка белых
				b += (mtx[i][j] == 2); // пешка черных
				bq += (mtx[i][j] == 4); // дамка черных
                if (scoring_mode == "NumberAndPotential")
                {
                    w += 0.05 * (mtx[i][j] == 1) * (7 - i); // насколько далеко пешка белых от дамки
					b += 0.05 * (mtx[i][j] == 2) * (i); // насколько далеко пешка черных от дамки
                }
            }
        }
        // мы считаем очки для черных
        if (!first_bot_color)
        {
            swap(b, w);
            swap(bq, wq);
        }
        
		// если у белых нет шашек, то избегаем деления на ноль
        if (w + wq == 0)
            return INF;
        if (b + bq == 0)
            return 0;
        int q_coef = 4;
        if (scoring_mode == "NumberAndPotential")
        {
            // усиливаем коэффициент дамки
            q_coef = 5;
        }
        return (b + bq * q_coef) / (w + wq * q_coef);
    }

    double find_first_best_turn(std::vector<std::vector<POS_T>> mtx,
        const bool color,
        const POS_T x,
        const POS_T y,
        size_t state,
        double alpha /*= -1*/)
    {
        // Убедимся, что для текущего узла есть плейсхолдеры
        if (state >= next_move.size())
        {
            next_best_state.push_back(-1);
            next_move.emplace_back(-1, -1, -1, -1);
        }

        // Получаем список доступных ходов:
        // если x,y заданы - продолжаем бить той же шашкой, иначе ищем по цвету
        if (x != -1) find_turns(x, y, mtx);
        else         find_turns(color, mtx);

        const auto local_turns = turns;
        const bool forced_beat = have_beats;

        // Если побитий нет и это не корневой "пустой" ход продолжения -
        // передаём ход оппоненту на обычный рекурсивный просчёт
        if (!forced_beat && x != -1)
        {
            return find_best_turns_rec(mtx, 1 - color, /*depth=*/0, alpha);
        }

        // Если совсем нет ходов — оценим позицию как проигранную/выигранную на этом уровне
        if (local_turns.empty())
        {
            return 0.0; // при продолжении цепочки побитий отсутствие ходов => конец цепи
        }

        double best_score = -1.0;
        int    best_next_state = -1;
        move_pos best_move(-1, -1, -1, -1);

        // Перебираем все ходы из текущего положения
        for (const auto& mv : local_turns)
        {
            // Готовим дочернее состояние для восстановления линии
            const size_t child_state = next_move.size();
            next_best_state.push_back(-1);
            next_move.emplace_back(-1, -1, -1, -1);

            const auto next_mtx = make_turn(mtx, mv);

            double score;
            if (forced_beat)
            {
                // Продолжаем цепочку побитий той же шашкой (ход того же цвета, глубина не растёт)
                score = find_first_best_turn(next_mtx, color, mv.x2, mv.y2, child_state, best_score);
            }
            else
            {
                // Обычный ход: передаём ход сопернику и считаем дальнейший расклад
                score = find_best_turns_rec(next_mtx, 1 - color, /*depth=*/0, /*alpha=*/best_score);
            }

            if (score > best_score)
            {
                best_score = score;
                best_move = mv;
                best_next_state = forced_beat ? int(child_state) : -1;

                // Пишем лучший на текущий момент результат в корень состояния
                next_move[state] = best_move;
                next_best_state[state] = best_next_state;

                // Простейшее "псевдо"-альфа: для ускорения отсекаем явные аутсайдеры
                if (optimization != "O0" && alpha >= 0.0 && best_score > alpha)
                    alpha = best_score;
            }
        }

        return best_score;
    }

    double find_best_turns_rec(std::vector<std::vector<POS_T>> mtx,
        const bool color,
        const size_t depth,
        double alpha = -1,
        double beta = INF + 1,
        const POS_T x = -1,
        const POS_T y = -1)
    {
        // Лист: достигнута максимальная глубина — оцениваем позицию
        if (depth == static_cast<size_t>(Max_depth))
        {
            // Соответствие исходному контракту оценки:
            // кто является "макс"-игроком определяется parity(depth) и color
            return calc_score(mtx, (depth % 2 == color));
        }

        // Генерируем ходы: продолжение цепочки для конкретной шашки или общий поиск по цвету
        if (x != -1) find_turns(x, y, mtx);
        else         find_turns(color, mtx);

        const bool forced_beat = have_beats;
        const auto local_turns = turns;

        // Если мы находимся в режиме продолжения конкретной шашки (x!=-1),
        // но побитий нет — ход переходит сопернику, глубина увеличивается.
        if (!forced_beat && x != -1)
        {
            return find_best_turns_rec(mtx, 1 - color, depth + 1, alpha, beta);
        }

        // Нет ходов вообще — терминальное состояние: победа/поражение по ходу
        if (local_turns.empty())
        {
            return (depth % 2 ? 0.0 : double(INF));
        }

        // Минимакс с альфа-бета отсечениями.
        // Чётная глубина — минимизатор, нечётная — максимизатор (как и раньше).
        double best_min = INF + 1.0;
        double best_max = -1.0;

        for (const auto& mv : local_turns)
        {
            const auto next_mtx = make_turn(mtx, mv);
            double val;

            if (forced_beat || x != -1)
            {
                // Если есть обязательные побития или мы продолжаем цепочку,
                // ход остаётся за тем же цветом и глубина не меняется.
                val = find_best_turns_rec(next_mtx, color, depth,
                    alpha, beta, mv.x2, mv.y2);
            }
            else
            {
                // Обычный ход: передаём очередь сопернику и увеличиваем глубину.
                val = find_best_turns_rec(next_mtx, 1 - color, depth + 1,
                    alpha, beta);
            }

            // Обновляем экстремумы
            if (val < best_min) best_min = val;
            if (val > best_max) best_max = val;

            // Альфа-бета: на нечётной глубине максимизируем, на чётной — минимизируем
            if (depth % 2)
            {
                // max-слой
                if (val > alpha) alpha = val;
            }
            else
            {
                // min-слой
                if (val < beta) beta = val;
            }

            if (optimization != "O0" && alpha >= beta)
            {
                // Небольшой сдвиг, как и раньше, чтобы стабилизировать возврат
                return (depth % 2 ? best_max + 1.0 : best_min - 1.0);
            }
        }

        return (depth % 2 ? best_max : best_min);
    }

public:
    // поиск хода для цвета игрока
    void find_turns(const bool color)
    {
        find_turns(color, board->get_board());
    }

	// поиска хода для шашки в позиции (x, y)
    void find_turns(const POS_T x, const POS_T y)
    {
        find_turns(x, y, board->get_board());
    }

private:
    // поиск хода для цвета игрока
    void find_turns(const bool color, const vector<vector<POS_T>> &mtx)
    {
        vector<move_pos> res_turns;
        bool have_beats_before = false;
        for (POS_T i = 0; i < 8; ++i)
        {
            for (POS_T j = 0; j < 8; ++j)
            {
                if (mtx[i][j] && mtx[i][j] % 2 != color)
                {
                    find_turns(i, j, mtx);
                    if (have_beats && !have_beats_before)
                    {
                        have_beats_before = true;
                        res_turns.clear();
                    }
                    if ((have_beats_before && have_beats) || !have_beats_before)
                    {
                        res_turns.insert(res_turns.end(), turns.begin(), turns.end());
                    }
                }
            }
        }
        turns = res_turns;
        shuffle(turns.begin(), turns.end(), rand_eng);
        have_beats = have_beats_before;
    }

    // поиска хода для шашки в позиции (x, y)
    void find_turns(const POS_T x, const POS_T y, const vector<vector<POS_T>> &mtx)
    {
        turns.clear();
        have_beats = false;
        POS_T type = mtx[x][y];
        // check beats
        switch (type)
        {
        case 1:
        case 2:
            // check pieces
            for (POS_T i = x - 2; i <= x + 2; i += 4)
            {
                for (POS_T j = y - 2; j <= y + 2; j += 4)
                {
                    if (i < 0 || i > 7 || j < 0 || j > 7)
                        continue;
                    POS_T xb = (x + i) / 2, yb = (y + j) / 2;
                    if (mtx[i][j] || !mtx[xb][yb] || mtx[xb][yb] % 2 == type % 2)
                        continue;
                    turns.emplace_back(x, y, i, j, xb, yb);
                }
            }
            break;
        default:
            // check queens
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    POS_T xb = -1, yb = -1;
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j)
                    {
                        if (mtx[i2][j2])
                        {
                            if (mtx[i2][j2] % 2 == type % 2 || (mtx[i2][j2] % 2 != type % 2 && xb != -1))
                            {
                                break;
                            }
                            xb = i2;
                            yb = j2;
                        }
                        if (xb != -1 && xb != i2)
                        {
                            turns.emplace_back(x, y, i2, j2, xb, yb);
                        }
                    }
                }
            }
            break;
        }
        // check other turns
        if (!turns.empty())
        {
            have_beats = true;
            return;
        }
        switch (type)
        {
        case 1:
        case 2:
            // check pieces
            {
                POS_T i = ((type % 2) ? x - 1 : x + 1);
                for (POS_T j = y - 1; j <= y + 1; j += 2)
                {
                    if (i < 0 || i > 7 || j < 0 || j > 7 || mtx[i][j])
                        continue;
                    turns.emplace_back(x, y, i, j);
                }
                break;
            }
        default:
            // check queens
            for (POS_T i = -1; i <= 1; i += 2)
            {
                for (POS_T j = -1; j <= 1; j += 2)
                {
                    for (POS_T i2 = x + i, j2 = y + j; i2 != 8 && j2 != 8 && i2 != -1 && j2 != -1; i2 += i, j2 += j)
                    {
                        if (mtx[i2][j2])
                            break;
                        turns.emplace_back(x, y, i2, j2);
                    }
                }
            }
            break;
        }
    }

  public:
    // список ходов
    vector<move_pos> turns;
	// были ли ходы с выбиванием шашек
    bool have_beats;
	// максимальный уровень просчета ходов
    int Max_depth;

  private:
	  // генератор случайных чисел для перемешивания ходов
    default_random_engine rand_eng;
	// режим подсчета очков
    string scoring_mode;
	// уровень оптимизации альфа-бета отсечения
    string optimization;
	// следующий ход
    vector<move_pos> next_move;
	// следующий статус доски после хода
    vector<int> next_best_state;
	// указатели на доску
    Board *board;
	// указатель на конфиг
    Config *config;
};
